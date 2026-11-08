#pragma once

#include "Game/NPC/NPCActor.hpp"

class Peach : public NPCActor {
public:
    Peach(const char*);

    virtual void init(const TableIter&);

    void exeWait();
};
