#pragma once

#include "Game/NameObj/NameObj.hpp"

class OceanRing;

class OceanRingBloomDrawer : public NameObj {
public:
    OceanRingBloomDrawer(OceanRing*);

    virtual ~OceanRingBloomDrawer() override;
    virtual void init(const TableIter&) override;
    virtual void draw() const override;

    OceanRing* mRing;
};