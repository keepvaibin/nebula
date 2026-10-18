#include "Game/MapObj/MapPartsRailPointPassChecker.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"

MapPartsRailPointPassChecker::MapPartsRailPointPassChecker(LiveActor* pActor) : MapPartsFunction(pActor, "\203\214\201\133\203\213\223\137\222\312\211\337\203\140\203\106\203\142\203\112\201\133" /* レール点通過チェッカー */) {
    mRailPointNo = -1;
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
    if (MR::getCurrentRailPointNo(mHost) > 0) {
        return false;
    }

    return MR::getCurrentRailPointNo(mHost) != mRailPointNo;
}

bool MapPartsRailPointPassChecker::isPassedEndPoint() const {
    LiveActor* host = mHost;
    if (MR::getCurrentRailPointNo(host) != MR::getRailPointNum(host) - 1) {
        return false;
    }

    return MR::getCurrentRailPointNo(host) != mRailPointNo;
}

MapPartsRailPointPassChecker::~MapPartsRailPointPassChecker() {
}

bool MapPartsRailPointPassChecker::isWorking() const {
    return true;
}
