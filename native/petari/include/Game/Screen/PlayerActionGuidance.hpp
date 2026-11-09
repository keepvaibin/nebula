#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SimpleLayout;

class PlayerActionGuidance : public LayoutActor {
public:
    /// @brief Creates a new `PlayerActionGuidance`.
    PlayerActionGuidance();

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    void createSpinLayout();
    void createTamakoroLayout();
    // The ball tutorial's picture this frame, in `state` (0 raise, 1 hold, 2 raise OK, 3 rotate).
    void forceTamakoro(s32 state);
    void exeWaitFocusIn();
    void exeFadein();
    void startWaitAnimTamakoro();
    void exeDisplay();
    void exeSuspend();
    void exeFadeout();
    void startAnimAllLayout(const char*);
    void startWaitAnimAllLayout();
    void setAnimFrameAndStopAllLayout(f32);
    void hideAllLayout();
    bool isInVolumePlayer(const char*, bool) const;

    SimpleLayout* mSpinLayout;
    SimpleLayout* mTamakoroLayout;
    LayoutActor* mCurrentLayout;
    s32 mGuidanceState;
    s32 mGuidancePrevState;
    bool mShow;           // a guidance volume or the tutorial wants a picture this frame
    bool mTamakoroForced; // the ball tutorial asked for its picture this frame
};

namespace MR {
    PlayerActionGuidance* getPlayerActionGuidance();
};  // namespace MR
