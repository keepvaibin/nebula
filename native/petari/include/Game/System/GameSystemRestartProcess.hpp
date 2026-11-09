#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ValueControl;

// Back to the title, a restart or leaving the game: the picture fades out and the sound stops, then the game goes on from
// the title (fading in) or the host restarts or closes it.
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
    virtual void control() override;

    bool isActive() const;
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
};
