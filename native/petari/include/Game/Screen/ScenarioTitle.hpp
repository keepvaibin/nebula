#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ScenarioTitle : public LayoutActor {
public:
    ScenarioTitle();

    virtual void init(const TableIter&);

    void start();
    void end();
    void exeAppear();
    void exeWait();
    void exeEnd();
};
