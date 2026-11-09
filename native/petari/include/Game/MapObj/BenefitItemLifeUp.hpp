#pragma once

#include "Game/MapObj/BenefitItemObj.hpp"

class BenefitItemLifeUp : public BenefitItemObj {
public:
    BenefitItemLifeUp(const char*);

    virtual void init(const TableIter& rIter) override {
        BenefitItemObj::init(rIter);
    }
    virtual void initModelAndEfx() override;
    virtual void exeCatch() override;
    virtual void runEfx(const char*) override;
    virtual void stopEfx(const char*) override;
};
