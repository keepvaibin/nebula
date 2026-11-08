#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CoinCounter;
class MarioMeter;
class PlayerLeft;
class StarCounter;
class StarPieceCounter;

class CounterLayoutController : public LayoutActor {
public:
    /// @brief Creates a new `CounterLayoutController`.
    CounterLayoutController();

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();

    void forceAppear();
    void setHPMeter(MarioMeter*);
    void requestedTicoEat(bool);
    static bool isInvalidSystemStateShowCounter();
    static bool isWaitToDisappearCounter(const LayoutActor*);
    void showAllLayout();
    void hideAllLayout();
    bool isHiddenOrWaitAllLayout() const;
    bool isPlayerMoving() const;
    void appearAllCounterWithoutStar();
    void killAllCoounter();
    bool tryEndTicoEatStarPiece();
    void exePlayerMoving();
    void exePlayerNotMoving();

    s32 mPlayerNotMovingFrame;
    bool mIsTicoEatRequested;
    CoinCounter* mCoinCounter;
    StarPieceCounter* mStarPieceCounter;
    PlayerLeft* mPlayerLeft;
    StarCounter* mStarCounter;
    MarioMeter* mHPMeter;
};
