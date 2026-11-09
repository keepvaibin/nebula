#pragma once

#include "Game/NPC/NPCActor.hpp"

class ButlerExplain : public NPCActor {
public:
    ButlerExplain(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    void startDemo();

    void exeDemo();
};
