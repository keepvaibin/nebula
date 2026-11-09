#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ChangeBgmCube : public AreaObj {
public:
    ChangeBgmCube(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    u8 mIsInside;
};
