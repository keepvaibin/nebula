#include "Game/Util/StringUtil.hpp"

namespace {
    struct PlacedHiddenStarInfo {
        const char* mStageName;
        s32 mHiddenScenarioNo;
        s32 mPlacedScenarioNo;
    };

    const PlacedHiddenStarInfo cPlacedHiddenStarInfoTable[] = {
        {"HeavenlyBeachGalaxy", 6, 3},
    };
};  // namespace

namespace MR {
    s32 getPlacedHiddenStarScenarioNoFromTable(const char* pStageName, s32 scenarioNo) {
        for (u32 i = 0; i < ARRAY_SIZE(::cPlacedHiddenStarInfoTable); i++) {
            const PlacedHiddenStarInfo& rInfo = ::cPlacedHiddenStarInfoTable[i];
            if (MR::isEqualString(pStageName, rInfo.mStageName) && scenarioNo == rInfo.mHiddenScenarioNo) {
                return rInfo.mPlacedScenarioNo;
            }
        }

        return -1;
    }
};  // namespace MR
