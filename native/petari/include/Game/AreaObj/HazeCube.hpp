#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class HazeCube : public AreaObj {
public:
    HazeCube(int, const char*);

    virtual void init(const TableIter&);

    virtual const char* getManagerName() const {
        return "HazeCube";
    }

    f32 mDistance;
};
