#pragma once

#include "Game/NameObj/NameObj.hpp"

class OceanBowl;

class OceanBowlBloomDrawer : public NameObj {
public:
    OceanBowlBloomDrawer(OceanBowl*);

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;

    OceanBowl* mHost;
};