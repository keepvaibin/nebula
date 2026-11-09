#pragma once

#include "Game/NameObj/NameObj.hpp"

class OceanBowl;

class OceanBowlBloomDrawer : public NameObj {
public:
    OceanBowlBloomDrawer(OceanBowl* pHost);

    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;

    OceanBowl* mHost;
};