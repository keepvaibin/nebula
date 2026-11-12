#include "Game/System/GameDataPlayerStatus.hpp"
#include "Game/System/GameRules.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/SaveBytes.hpp"

namespace {
    // The lives the player starts with and has again on every load (4).
    const s32 cStartingPlayerLeft = 4;
    // The most lives (99).
    const s32 cMaxPlayerLeft = 99;
};  // namespace

GameDataPlayerStatus::GameDataPlayerStatus() {
    initializeData();
}

u32 GameDataPlayerStatus::makeHeaderHashCode() const {
    return 0x27C90F;
}

u32 GameDataPlayerStatus::getSignature() const {
    return 'PLAY';
}

s32 GameDataPlayerStatus::serialize(u8* pBuffer, u32 size) const {
    SaveWriter stream(pBuffer, size);
    stream.write(&mStoryProgress, sizeof(mStoryProgress));
    stream.write(&mStockedStarPiece, sizeof(mStockedStarPiece));
    // Read back as mPlayerLeftSupply; the lives start again on every load.
    stream.write(&mPlayerLeft, sizeof(mPlayerLeft));
    return stream.getPosition();
}

s32 GameDataPlayerStatus::deserialize(const u8* pBuffer, u32 size) {
    initializeData();

    SaveReader stream(pBuffer, size);

    stream.read(&mStoryProgress, sizeof(mStoryProgress));
    stream.read(&mStockedStarPiece, sizeof(mStockedStarPiece));

    if (stream.getAvailable() != 0) {
        stream.read(&mPlayerLeftSupply, sizeof(mPlayerLeftSupply));
    }

    mPlayerLeft = ::cStartingPlayerLeft;

    return 0;
}

void GameDataPlayerStatus::initializeData() {
    mPlayerLeft = ::cStartingPlayerLeft;
    mPlayerLeftSupply = 4;
    mStockedStarPiece = 0;
    mStoryProgress = 0;
}

s32 GameDataPlayerStatus::getPlayerLeft() const {
    s32 playerLeft = mPlayerLeft;

    return MR::clamp(playerLeft, 0, ::cMaxPlayerLeft);
}

void GameDataPlayerStatus::addPlayerLeft(int num) {
    s32 playerLeft = mPlayerLeft + num;

    mPlayerLeft = MR::clamp(playerLeft, 0, ::cMaxPlayerLeft);
}

bool GameDataPlayerStatus::isPlayerLeftSupply() const {
    return mPlayerLeftSupply >= 10;
}

void GameDataPlayerStatus::offPlayerLeftSupply() {
    mPlayerLeftSupply = 0;
}

void GameDataPlayerStatus::addStockedStarPiece(int num) {
    s32 stockedStarPiece = mStockedStarPiece + num;

    mStockedStarPiece = MR::clamp(stockedStarPiece, 0, GameRules::maxStarBits());
}
