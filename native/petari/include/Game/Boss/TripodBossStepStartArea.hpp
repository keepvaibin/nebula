#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class TripodBossStepStartArea : public AreaObj {
public:
    TripodBossStepStartArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual const char* getManagerName() const override;

    s32 mSequenceNo;
};
