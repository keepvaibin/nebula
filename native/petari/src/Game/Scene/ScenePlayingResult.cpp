#include "Game/Scene/ScenePlayingResult.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaHost.hpp"

#define COIN_NUM_MIN 0
#define COIN_NUM_MAX NebulaHost::setting(NEBULA_PATCH_MAX_COINS, 999)
#define COIN_STEP_NUM NebulaHost::setting(NEBULA_PATCH_COINS_PER_ONE_UP, 50)
#define PURPLE_COIN_POWER_STAR_APPEAR_NUM NebulaHost::setting(NEBULA_PATCH_PURPLE_COINS_FOR_STAR, 100)
#define STAR_PIECE_STEP_NUM NebulaHost::setting(NEBULA_PATCH_STAR_BITS_PER_ONE_UP, 50)

namespace {
    void requestOneUpIfNeed(int prevNum, int currNum, int stepNum) {
        if (stepNum > 0 && prevNum / stepNum < currNum / stepNum) {
            MR::incPlayerLeft();
            MR::requestOneUp();
        }
    }
};  // namespace

void ScenePlayingResult::incCoin(int num) {
    s32 prevNum = mCoinNum;
    s32 currNum = MR::clamp(mCoinNum + num, COIN_NUM_MIN, COIN_NUM_MAX);
    mCoinNum = currNum;
    NebulaAchievements::countStat(NebulaStat_Coins, num);

    ::requestOneUpIfNeed(prevNum, currNum, COIN_STEP_NUM);
}

s32 ScenePlayingResult::getCoinNum() const {
    return MR::clamp(mCoinNum, COIN_NUM_MIN, COIN_NUM_MAX);
}

void ScenePlayingResult::incPurpleCoin() {
    mPurpleCoinNum++;

    if (mPurpleCoinNum == PURPLE_COIN_POWER_STAR_APPEAR_NUM) {
        MR::requestAppearPowerStarCoin100();
    }
}

void ScenePlayingResult::incStarPiece(int num) {
    s32 prevNum = GameDataFunction::getStarPieceNum();
    GameDataFunction::addStarPiece(num);
    s32 currNum = GameDataFunction::getStarPieceNum();
    s32 last1upNum = GameDataFunction::getLast1upStarPieceNum();

    if (MR::isStageAstroLocation() || STAR_PIECE_STEP_NUM <= 0) {
        return;
    }

    if (last1upNum + STAR_PIECE_STEP_NUM > currNum) {
        return;
    }

    if (prevNum / STAR_PIECE_STEP_NUM < currNum / STAR_PIECE_STEP_NUM) {
        MR::incPlayerLeft();
        MR::requestOneUp();
    }

    GameDataFunction::setLast1upStarPieceNum(last1upNum + STAR_PIECE_STEP_NUM);
}

s32 ScenePlayingResult::getStarPieceNum() const {
    return GameDataFunction::getStarPieceNum();
}

ScenePlayingResult::~ScenePlayingResult() {
}

ScenePlayingResult::ScenePlayingResult() : NameObj("\203\166\203\214\203\103\214\213\211\312\225\333\216\235" /* プレイ結果保持 */), mCoinNum(0), mPurpleCoinNum(0) {
}
