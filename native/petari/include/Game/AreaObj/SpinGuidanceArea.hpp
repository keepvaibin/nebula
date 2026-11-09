#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class SpinGuidanceArea : public AreaObj {
public:
    SpinGuidanceArea(int, const char*);

    virtual void init(const TableIter&) override;
};
