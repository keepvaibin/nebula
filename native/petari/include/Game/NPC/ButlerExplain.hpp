#pragma once

#include "Game/NPC/NPCActor.hpp"

class ButlerExplain : public NPCActor {
public:
    ButlerExplain(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();

    void startDemo();

    void exeDemo();
};
