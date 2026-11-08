#pragma once

#include "Game/Screen/LayoutActor.hpp"

class LiveActor;
class SimpleLayout;

class FullnessMeter : public LayoutActor {
public:
    FullnessMeter(LiveActor*, s32, s32);

    virtual void init(const TableIter&);
    virtual void control();

    void setNumber(s32);
    void requestAppear();
    void requestDisappear();
    void pauseOff();
    void exeAppear();
    void exeWait();
    void exeEnd();

    s32 mNumber;
    s32 mNumberMax;
    LiveActor* mHost;
    SimpleLayout* mTargetCounter;
};
