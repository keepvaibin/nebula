#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ChangeBgmCube : public AreaObj {
public:
    ChangeBgmCube(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    u8 mIsInside;
};
