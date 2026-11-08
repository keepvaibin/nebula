#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DinoPackunDemoPosition : public LiveActor {
public:
    DinoPackunDemoPosition(const char* pName);

    virtual ~DinoPackunDemoPosition();
    virtual void init(const TableIter&);
    virtual void control();

    void startOpeningDemo();
    void endDemo();
    void exeOpeningDemo();
};
