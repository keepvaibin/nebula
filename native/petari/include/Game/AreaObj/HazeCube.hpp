#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class HazeCube : public AreaObj {
public:
    HazeCube(int, const char*);

    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "HazeCube";
    }

    f32 mDistance;
};
