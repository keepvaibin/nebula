#pragma once

#include "Game/MapObj/NormalMapBase.hpp"

class NormalMapTestObj : public NormalMapBase {
public:
    NormalMapTestObj(const char*);

    virtual void init(const TableIter&);
};
