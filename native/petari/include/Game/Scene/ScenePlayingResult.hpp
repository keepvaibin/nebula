#pragma once

#include "Game/NameObj/NameObj.hpp"

// The coins and purple coins of the stage being played; star bits go straight to the save.
class ScenePlayingResult : public NameObj {
public:
    ScenePlayingResult();

    void incCoin(int num);
    bool payCoin(s32 num);
    s32 getCoinNum() const;
    void incPurpleCoin();
    void incStarPiece(int num);
    s32 getStarPieceNum() const;

    s32 mCoinNum;
    s32 mPurpleCoinNum;
};
