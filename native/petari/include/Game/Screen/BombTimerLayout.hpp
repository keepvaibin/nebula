#pragma once

#include "Game/Screen/LayoutActor.hpp"

class BombTimerLayout : public LayoutActor {
public:
    BombTimerLayout(bool isConnectToScene);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;

    void setTimeLimit(u32 timeLimit);
    void suspend();
    void resume();
    bool isReadyToTimeUp() const;
    void addFrame();
    bool update();
    u32 getRestTime() const;
    void updateTextBox();
    void exeAppear();
    void exeWait();
    void exeDanger();
    void exeEnd();

    u32 mTimeLimit;
    u32 mDangerTransFrame;
    u32 mFrame;
    bool mIsSuspend;
};
