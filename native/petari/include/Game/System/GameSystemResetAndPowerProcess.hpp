#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/Screen/LayoutActor.hpp"

class ValueControl;

class GameSystemResetAndPowerProcess : public LayoutActor {
public:
    enum ResetOperation {
        ResetOperation_ApplicationReset,
        ResetOperation_Restart,
        ResetOperation_Quit,
    };

    /// @brief Creates a new `GameSystemResetAndPowerProcess`.
    GameSystemResetAndPowerProcess();

    virtual ~GameSystemResetAndPowerProcess();

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void control();

    bool isActive() const;
    void setResetOperationApplicationReset();
    void setResetOperationQuit();
    void requestReset(bool);
    void requestQuitGame(bool);
    void exeIdle();
    void exeWaitResetPermitted();
    void exePrepareReset();
    void exeReset();
    void exeWaitPrepareFadein();
    void exeFadein();
    void exitApplication();
    bool tryPermitReset();
    bool isResetAcceptAudio() const;

    ValueControl* mFadeinoutControl;
    NebulaAtomic< s32 > mResetOperation;
    bool _5C;
    bool _5E;
};
