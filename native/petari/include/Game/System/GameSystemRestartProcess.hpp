#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ValueControl;

// Back to the title, a restart or leaving the game: the picture fades out and the sound stops, then the game goes on from
// the title (fading in) or the host restarts or closes it. The host's reset (NEBULA_HOME_MENU_RESET) goes back to the
// title with the saves read again; at a diagnostics run's start the picture then stays black under the run's panel
// while the game goes on to the run's first stage, and fades in when it shows (drawHoldCover).
class GameSystemRestartProcess : public LayoutActor {
public:
    enum ResetOperation {
        ResetOperation_ApplicationReset,
        ResetOperation_Restart,
        ResetOperation_Quit,
    };

    GameSystemRestartProcess();

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    // While a diagnostics run is going, the reset's fade and the black kept after it, drawn under the run's panel
    // (GameSystem::draw).
    void drawHoldCover() const;
    virtual void control() override;

    bool isActive() const;
    // The screen black: a reset faded all the way out, or the black kept after a diagnostics run's reset.
    bool isScreenBlack() const;
    void setResetOperationApplicationReset();
    void setResetOperationQuit();
    // Ignored while one runs.
    void requestReset(bool isImmediate);
    void requestQuitGame(bool isImmediate);
    void exeIdle();
    void exeFadeOut();
    void exeWaitPrepareFadein();
    void exeFadein();

private:
    void beginFadeOut(bool isImmediate);
    void reset();
    void exitApplication();

    ValueControl* mFadeinoutControl;
    s32 mResetOperation;
    bool mReloadSaves;  // the host's reset: the saves are read again before the title
    bool mHoldDark;     // black after the reset until the diagnostics run's first stage shows
    bool mHoldFading;   // that black fading in
    s32 mHoldSteps;     // game steps held black
};
