#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/AlreadyDoneFlagInGalaxy.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    // The most star bits carried in a stage (999).
    const s32 cMaxCarriedStarPieceNum = 999;
};  // namespace

GameDataTemporaryInGalaxy::GameDataTemporaryInGalaxy() : mPlayerRestartIdInfo(nullptr), mAlreadyDoneFlag(nullptr) {
    mPlayerRestartIdInfo = new PlacementId(MR::getInitializeStartIdInfo());
    mAlreadyDoneFlag = new AlreadyDoneFlagInGalaxy(64);

    resetStageResultParam();

    mIsRetry = false;
}

void GameDataTemporaryInGalaxy::resetPlayerRestartIdInfo() {
    *mPlayerRestartIdInfo = MR::getInitializeStartIdInfo();
}

void GameDataTemporaryInGalaxy::setPlayerRestartIdInfo(const PlacementId& rPlayerRestartIdInfo) {
    *mPlayerRestartIdInfo = rPlayerRestartIdInfo;
}

bool GameDataTemporaryInGalaxy::isStageCleared() const {
    return mClearState == 1;
}

void GameDataTemporaryInGalaxy::resetStageResultPowerStarParam() {
    mClearState = 0;
    mPowerStarId = 1;
    mStageName = "";
}

void GameDataTemporaryInGalaxy::resetStageResultStarPieceParam() {
    mStarPieceNum = 0;
    mLast1upStarPieceNum = 0;
}

void GameDataTemporaryInGalaxy::resetStageResultCoinParam() {
    mCoinNum = 0;
}

void GameDataTemporaryInGalaxy::addStarPiece(int num) {
    s32 starPiece = mStarPieceNum + num;

    mStarPieceNum = MR::clamp(starPiece, 0, ::cMaxCarriedStarPieceNum);
}

void GameDataTemporaryInGalaxy::setLast1upStarPieceNum(int num) {
    mLast1upStarPieceNum = num;
}

s32 GameDataTemporaryInGalaxy::getStarPieceNum() const {
    return MR::clamp(static_cast< s32 >(mStarPieceNum), 0, ::cMaxCarriedStarPieceNum);
}

void GameDataTemporaryInGalaxy::resetStageResultParam() {
    resetStageResultPowerStarParam();
    resetStageResultStarPieceParam();
    resetStageResultCoinParam();
}

void GameDataTemporaryInGalaxy::clearAlreadyDoneFlag() {
    mAlreadyDoneFlag->clear();
}

s32 GameDataTemporaryInGalaxy::setupAlreadyDoneFlag(const char* pName, const TableIter& rIter, u32* pIsDone) {
    return mAlreadyDoneFlag->setupFlag(pName, rIter, pIsDone);
}

void GameDataTemporaryInGalaxy::updateAlreadyDoneFlag(int index, u32 isDone) {
    mAlreadyDoneFlag->updateValue(index, isDone);
}

void GameDataTemporaryInGalaxy::receiveStageResultParam(const char* pStageName, s32 powerStarId, int starPieceNum, int coinNum) {
    resetStageResultParam();

    mPowerStarId = powerStarId;
    mClearState = 1;
    mStarPieceNum = starPieceNum;
    mCoinNum = coinNum;

    mStageName = MR::internName(pStageName);
}
