#include "Game/Scene/ScenePlayingResult.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, cCoinNumMin, 0);
    // Coins for a 1-up (50); 0: none.
    NEBULA_TUNABLE(s32, cCoinStepNum, 50);
    // Purple coins that bring out the Power Star (100).
    NEBULA_TUNABLE(s32, cPurpleCoinPowerStarAppearNum, 100);
    // Star bits for a 1-up (50); 0: none.
    NEBULA_TUNABLE(s32, cStarPieceStepNum, 50);

    s32 getCoinNumMax() {
        return GameRules::maxCoins();
    }

    s32 getCoinStepNum() {
        return ::cCoinStepNum;
    }

    s32 getPurpleCoinPowerStarAppearNum() {
        return ::cPurpleCoinPowerStarAppearNum;
    }

    s32 getStarPieceStepNum() {
        return ::cStarPieceStepNum;
    }

    // A 1-up each time the count crosses a multiple of stepNum; none for stepNum <= 0.
    void requestOneUpIfNeed(s32 prevNum, s32 currNum, s32 stepNum) {
        if (stepNum > 0 && prevNum / stepNum < currNum / stepNum) {
            MR::incPlayerLeft();
            MR::requestOneUp();
        }
    }
}  // namespace

void ScenePlayingResult::incCoin(int num) {
    const s32 prevNum = mCoinNum;
    const s32 currNum = MR::clamp(mCoinNum + num, ::cCoinNumMin, ::getCoinNumMax());
    mCoinNum = currNum;
    NebulaAchievements::countStat(NebulaStat_Coins, num);

    ::requestOneUpIfNeed(prevNum, currNum, ::getCoinStepNum());
}

s32 ScenePlayingResult::getCoinNum() const {
    return MR::clamp(mCoinNum, ::cCoinNumMin, ::getCoinNumMax());
}

void ScenePlayingResult::incPurpleCoin() {
    mPurpleCoinNum++;

    if (mPurpleCoinNum == ::getPurpleCoinPowerStarAppearNum()) {
        MR::requestAppearPowerStarCoin100();
    }
}

// Star bits give a 1-up at most once per step, counted from the last one.
void ScenePlayingResult::incStarPiece(int num) {
    const s32 prevNum = GameDataFunction::getStarPieceNum();
    GameDataFunction::addStarPiece(num);
    const s32 currNum = GameDataFunction::getStarPieceNum();
    const s32 last1upNum = GameDataFunction::getLast1upStarPieceNum();
    const s32 stepNum = ::getStarPieceStepNum();

    if (MR::isStageAstroLocation() || stepNum <= 0) {
        return;
    }

    if (last1upNum + stepNum > currNum) {
        return;
    }

    if (prevNum / stepNum < currNum / stepNum) {
        MR::incPlayerLeft();
        MR::requestOneUp();
    }

    GameDataFunction::setLast1upStarPieceNum(last1upNum + stepNum);
}

s32 ScenePlayingResult::getStarPieceNum() const {
    return GameDataFunction::getStarPieceNum();
}

ScenePlayingResult::ScenePlayingResult() : NameObj("プレイ結果保持"), mCoinNum(0), mPurpleCoinNum(0) {
    lumacore::setClass(this, lumacore::kClassId< ScenePlayingResult >);
}
