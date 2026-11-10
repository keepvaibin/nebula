#include "Game/MapObj/MapPartsRailPointPassChecker.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

MapPartsRailPointPassChecker::MapPartsRailPointPassChecker(LiveActor* pActor)
    : MapPartsFunction(pActor, "レール点通過チェッカー"), mRailPointNo(-1) {
    lumacore::setClass(this, lumacore::kClassId< MapPartsRailPointPassChecker >);
}

void MapPartsRailPointPassChecker::movement() {
    mRailPointNo = MR::getCurrentRailPointNo(mHost);
}

void MapPartsRailPointPassChecker::start() {
    mRailPointNo = MR::getCurrentRailPointNo(mHost);
}

bool MapPartsRailPointPassChecker::isPassed() const {
    return MR::getCurrentRailPointNo(mHost) != mRailPointNo;
}

bool MapPartsRailPointPassChecker::isReachedEnd() const {
    return MR::isRailReachedGoal(mHost);
}

bool MapPartsRailPointPassChecker::isPassedStartPoint() const {
    return MR::getCurrentRailPointNo(mHost) <= 0 && isPassed();
}

bool MapPartsRailPointPassChecker::isPassedEndPoint() const {
    return MR::getCurrentRailPointNo(mHost) == MR::getRailPointNum(mHost) - 1 && isPassed();
}

bool MapPartsRailPointPassChecker::isWorking() const {
    return true;
}
