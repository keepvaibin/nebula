#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class LightArea : public AreaObj {
public:
    LightArea(int, const char*);

    virtual void init(const TableIter&);

    virtual const char* getManagerName() const {
        return "LightArea";
    }

    s32 mPlacedZoneID;
};
