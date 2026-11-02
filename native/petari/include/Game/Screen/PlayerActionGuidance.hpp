#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SimpleLayout;

class PlayerActionGuidance : public LayoutActor {
public:
    /// @brief Creates a new `PlayerActionGuidance`.
    PlayerActionGuidance();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    void createSpinLayout();
    void createTamakoroLayout();
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
    bool _34;
    bool _35;
    bool _36;
    bool _37;
};

namespace MR {
    PlayerActionGuidance* getPlayerActionGuidance();
};  // namespace MR
