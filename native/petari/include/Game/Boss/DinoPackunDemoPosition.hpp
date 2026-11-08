#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DinoPackunDemoPosition : public LiveActor {
public:
    DinoPackunDemoPosition(const char* pName);

    virtual ~DinoPackunDemoPosition();
    virtual void init(const TableIter& rIter);

    void startOpeningDemo();
    void endDemo();
    void exeOpeningDemo();
};
