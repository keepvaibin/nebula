#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class LightArea : public AreaObj {
public:
    LightArea(int, const char*);

    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "LightArea";
    }

    s32 mPlacedZoneID;
};
