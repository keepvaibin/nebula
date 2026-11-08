#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

GalaxyMoveArgument::GalaxyMoveArgument(u32 moveType, const char* pStageName, s32 scenarioNo, const PlacementId* pMapIdInfo)
    : mMoveType(moveType), mStageName(pStageName), mScenarioNo(scenarioNo), mSelectedScenarioNo(-1) {
    mIDInfo.mId = -1;
    mIDInfo.mZoneID = -1;

    if (pMapIdInfo != nullptr) {
        mIDInfo = *pMapIdInfo;
    } else {
        mIDInfo = MR::getInitializeStartIdInfo();
    }
}

bool GalaxyMoveArgument::isEqualStage(const char* pStageName) const {
    if (mStageName == nullptr) {
        return false;
    }

    return MR::isEqualString(pStageName, mStageName);
}

bool GalaxyMoveArgument::isEqualStageScenario(const char* pStageName, int scenarioNo) const {
    return isEqualStage(pStageName) && scenarioNo == mScenarioNo;
}
