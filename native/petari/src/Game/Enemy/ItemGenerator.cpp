#include "Game/Enemy/ItemGenerator.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const f32 cStarPieceSpeedH = 10.0f;
    const f32 cStarPieceSpeedV = 40.0f;
}  // namespace

ItemGenerator::ItemGenerator() : mNum(1), mType(Type_Coin) {
}

void ItemGenerator::setTypeNone() {
    mType = Type_None;
    mNum = 0;
}

void ItemGenerator::setTypeCoin(s32 num) {
    mNum = num;
    mType = Type_Coin;
}

void ItemGenerator::setTypeStarPeace(s32 num) {
    mNum = num;
    mType = Type_StarPiece;
}

void ItemGenerator::generate(const LiveActor* pActor) {
    if (mNum <= 0) {
        return;
    }

    if (mType == Type_Coin) {
        MR::appearCoinPopToDirection(pActor, pActor->mPosition, -pActor->mGravity, mNum);
    } else if (mType == Type_StarPiece) {
        MR::appearStarPieceToDirection(pActor, pActor->mPosition, -pActor->mGravity, mNum, ::cStarPieceSpeedH, ::cStarPieceSpeedV, false);
        MR::startSound(pActor, isUseFarSE() ? "SE_OJ_STAR_PIECE_BURST_F" : "SE_OJ_STAR_PIECE_BURST");
    }
}

// The final Bowser fight hears the burst from afar.
bool ItemGenerator::isUseFarSE() const {
    return MR::isEqualStageName("KoopaBattleVs2Galaxy");
}
