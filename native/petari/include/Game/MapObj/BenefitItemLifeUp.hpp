#pragma once

#include "Game/MapObj/BenefitItemObj.hpp"

class BenefitItemLifeUp : public BenefitItemObj {
public:
    BenefitItemLifeUp(const char*);

    virtual void init(const TableIter& rIter) {
        BenefitItemObj::init(rIter);
    }
    virtual void initModelAndEfx();
    virtual void exeCatch();
    virtual void runEfx(const char*);
    virtual void stopEfx(const char*);
};
