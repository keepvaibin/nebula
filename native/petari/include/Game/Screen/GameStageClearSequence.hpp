#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SimpleEffectLayout;

class GameStageClearSequence : public LayoutActor {
public:
    /// @brief Creates a new `GameStageClearSequence`.
    GameStageClearSequence();

    virtual void init(const TableIter& rIter) override;

    void startPowerStarGetDemo();
    void startGrandStarGetDemo();
    void appearGetStarLayout();
    void exePowerStarGetDemo();
    void exeGrandStarGetDemo();

    SimpleEffectLayout* mEffectLayout;
    Vec2f mEffectFollowPos;
};
