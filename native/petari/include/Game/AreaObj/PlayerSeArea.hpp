#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class PlayerSeArea : public AreaObj {
public:
    PlayerSeArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "PlayerSeArea";
    }

    s32 mSeType;
};
